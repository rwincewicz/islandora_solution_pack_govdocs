<?php

/**
 * @file 
 * Islandora govdocs 
 */

/**
 * Islandora Govdocs
 */
class GovdocsCollectionClass extends CollectionClass {

  public static $COLLECTION_CLASS_COLLECTION_POLICY_STREAM = 'COLLECTION_POLICY';
  public static $COLLECTION_CLASS_COLLECTION_VIEW_STREAM = 'COLLECTION_VIEW';
  private $contentModelStream = NULL;
  private $collectionPolicyStream = NULL;
  private $collectionViewStream = NULL;
  public $collectionObject = NULL;

  /**
   * Creates a collection object. Optionally can associate it with a single collection with parameter $pid.
   *
   * @param string $pid The pid of the collection to represent.
   * @return CollectionClass
   */
  function __construct($pid = NULL) {
    if (!empty($pid)) {
      module_load_include('inc', 'fedora_repository', 'ObjectHelper');
      $this->collectionObject = new ObjectHelper($pid);
      $this->pid = $pid;
    }
  }

  /**
   * show field sets ?
   * @global type $base_url
   * @global type $user
   * @param type $page_number
   * @return string 
   */
  function showGovdocsFieldSets($page_number) {
    module_load_include('inc', 'fedora_repository', 'api/fedora_item');
    module_load_include('module', 'fedora_repository', 'fedora_repository');
    module_load_include('inc', 'fedora_repository', 'CollectionManagement');
    module_load_include('inc', 'fedora_repository', 'BatchIngest');
    module_load_include('inc', 'islandora_govdocs', 'add_pages');

    global $base_url;
    global $user;

    $tabset = array();

    $item = new Fedora_Item($this->pid);
    $query = NULL;
    $ds_array = $item->get_datastreams_list_as_array();

    if ($item->exists() && array_key_exists('QUERY', $item->datastreams)) {
      $query = $item->get_datastream_dissemination('QUERY');
    }

    $results = $this->getRelatedItems($this->pid, $query);

    $collection_items = $this->renderCollection($results, $this->pid, NULL, NULL, $page_number);

    $results_xml = new SimpleXMLElement($results);

    $item_list = array();
    
    foreach ($results_xml->results->result as $related) {
      $item_pid = explode('/', $related->object['uri']);
      $url = 'fedora/repository/' . $item_pid[1];
      $item_list[] = l($related->title, $url);
    }

    if (array_key_exists('PDF', $ds_array)) {
      $ds_info = $item->get_datastream_info('PDF');
      $item_list[] = l('PDF [' . _format_bytes($ds_info->datastream->size) . ']', 'fedora/repository/' . $this->pid . '/PDF/content');
    }

    $content = theme_item_list($item_list);
    
    $collection_item = new Fedora_Item($this->pid);
    // Check the form post to see if we are in the middle of an ingest operation.
    $show_ingest_tab = (!empty($_POST['form_id']) && $_POST['form_id'] == 'fedora_repository_ingest_form');
    $add_to_collection = $this->getIngestInterface();

    $show_batch_tab = TRUE;
    $policy = CollectionPolicy::loadFromCollection($this->pid, TRUE);
    $content_models = $policy->getContentModels();

    if (count($content_models) == 1 && $content_models[0]->pid == "islandora:collectionCModel") {
      $show_batch_tab = FALSE;
    }

    if (!$show_ingest_tab) {
      $view_selected = TRUE;
    }

    if (!$collection_items) {
      $view_selected = FALSE;
      $add_selected = TRUE;
    }

    $view_selected = !$show_ingest_tab;

    $form_state['storage']['content_model'] = 'islandora:govdocsCollectionCModel';
    $form_state['storage']['collection_pid'] = $this->pid;

    if ($results != NULL) {
//      if (array_key_exists('PDF', $ds_array)) {
        $tabset['view_tab'] = array(
          '#type' => 'tabpage',
          '#title' => 'View',
          '#selected' => $view_selected,
          '#content' => $content,
          '#weight' => -1,
        );
//      }
//      else {
//        $tabset['view_tab'] = array(
//          '#type' => 'tabpage',
//          '#title' => 'View',
//          '#selected' => $view_selected,
//          '#content' => $collection_items,
//          '#weight' => -1,
//        );
//      }
    }

    if (user_access('manage collections')) {
      $tabset['add_collection_tab'] = array(
        // #type and #title are the minimum requirements.
        '#type' => 'tabpage',
        '#title' => t('Manage This Collection'),
        // This will be the content of the tab.
        '#content' => drupal_get_form('govdocs_management_form', $this->pid),
        '#selected' => FALSE,
        '#weight' => 0,
      );
    }

    if (!array_key_exists('PDF', $ds_array) && user_access('ingest new fedora objects')) {
      $tabset['third_tab'] = array(
        '#type' => 'tabpage',
        '#title' => t('Add document'),
        '#content' => drupal_get_form('govdocs_add_pdf_form', $this->pid),
        '#weight' => -1,
      );
    }

//    if (array_key_exists('PDF', $ds_array) && user_access('purge objects and datastreams')) {
//      $tabset['fourth_tab'] = array(
//        '#type' => 'tabpage',
//        '#title' => t('Purge documents'),
//        '#selected' => $show_purge_tab,
//        '#content' => drupal_get_form('islandora_book_purge_pages_form', $this->pid),
//      );
//    }

//    if ($show_batch_tab && user_access('create batch process')) {
//      $tabset['batch_ingest_tab'] = array(
//        // #type and #title are the minimum requirements.
//        '#type' => 'tabpage',
//        '#title' => t('Batch Ingest'),
//        // This will be the content of the tab.
//        '#content' => drupal_get_form('batch_creation_form', $this->pid, $content_models),
//      );
//    }
    return $tabset;
  }

  function ingestMODS() {
    drupal_set_message('Ingested object ' . $this->pid);
  }

}

function _format_bytes($a_bytes) {
  if ($a_bytes < 1024) {
    return $a_bytes . ' B';
  }
  elseif ($a_bytes < 1048576) {
    return round($a_bytes / 1024, 0) . ' KB';
  }
  elseif ($a_bytes < 1073741824) {
    return round($a_bytes / 1048576, 0) . ' MB';
  }
  elseif ($a_bytes < 1099511627776) {
    return round($a_bytes / 1073741824, 0) . ' GB';
  }
  elseif ($a_bytes < 1125899906842624) {
    return round($a_bytes / 1099511627776, 0) . ' TB';
  }
  elseif ($a_bytes < 1152921504606846976) {
    return round($a_bytes / 1125899906842624, 0) . ' PB';
  }
  elseif ($a_bytes < 1180591620717411303424) {
    return round($a_bytes / 1152921504606846976, 0) . ' EB';
  }
  elseif ($a_bytes < 1208925819614629174706176) {
    return round($a_bytes / 1180591620717411303424, 0) . ' ZB';
  }
  else {
    return round($a_bytes / 1208925819614629174706176, 0) . ' YB';
  }
}